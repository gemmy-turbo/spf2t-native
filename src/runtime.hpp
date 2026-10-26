// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game.hpp"
#include "match.hpp"
#include "arcade.hpp"
#include "cabinet.hpp"
#include "coin_inputs.hpp"
#include <puzzle_fighter/source_coin_services.hpp>
#include <puzzle_fighter/match_scheduler.hpp>
#include <puzzle_fighter/match_interlude.hpp>
#include <puzzle_fighter/scene_overlays.hpp>
#include <puzzle_fighter/standalone_irq_services.hpp>
#include <functional>
namespace puzzle_fighter::prototype {
// Linux device delivery and the standalone final screen are explicit host
// boundaries. All normal logical IRQ/main ordering uses the source scheduler.
struct NativeMatchRuntime {
 Game&game;Match&match;MatchScheduler scheduler;MatchSpecialInterlude interlude;NativeArcadeSession arcade;NativeCabinetSession cabinet;
 CoinSwitchInputs coin_inputs;InputPorts ports;std::vector<std::array<std::uint32_t,3>> io;
 std::function<bool()> device_ready,ending_complete;
 std::function<void(SourceSoundCommand)> deliver_sound;
 std::function<void(std::uint8_t)> set_stereo;
 std::function<void()> winner_event,new_round_event;
 bool scene_requested=false;InterludeSceneRequest requested_scene=InterludeSceneRequest::arcade_opponent_selected;
 std::uint64_t irqs=0,dispatched_sounds=0;
 explicit NativeMatchRuntime(Game&g,Match&m):game(g),match(m),arcade(g,m,scheduler),cabinet(g,arcade,scheduler){arcade.credit_display_job=[this](){cabinet.run_credit_display_job();};bind_sound();}
 void bind_sound(){game.effects.sink=[this](SoundRequest r){queue_sound_request(Game::globals,game.memory,{r.number,r.pan,r.extra});};}
 void restart(){coin_inputs.clear();cabinet.stop();arcade.stop();scheduler={};scene_requested=false;io.clear();irqs=dispatched_sounds=0;clear_sound_requests(Game::globals,game.memory);bind_sound();}
 void begin_arcade(){restart();cabinet.cold();}
 // Explicit fixture/debug adapter retained for pre-boot source scene checks.
 void begin_paid_arcade(unsigned players=1){restart();arcade.begin(players);cabinet.owner=NativeCabinetSession::Owner::arcade;}
 void tick(std::array<std::uint16_t,2> input){
  // Final-screen observers still read memory after an inactive source frame.
  // Start a fresh host-frame budget even when no source work will run.
  game.memory.accesses=0;
  if(arcade.active?arcade.terminal:(match.post_complete||scene_requested))return;
  ports=input_ports_from_words(input);coin_inputs.apply(ports);io.clear();
  if(!scheduler.irq(Game::globals,game.memory,*this))throw std::runtime_error("Unsupported active cabinet service in native standalone IRQ");
  ++irqs;if(cabinet.owns_frame()){cabinet.frame();return;}arcade.before_main();
  if(arcade.active&&!arcade.normal_match()&&scheduler.continuation!=MatchContinuation::clear_first_wait&&scheduler.continuation!=MatchContinuation::clear_second_wait){arcade.frame();if(arcade.return_to_idle)cabinet.return_from_arcade();return;}
  if(scheduler.resume(Game::globals,game.memory,*this)==MatchRunResult::unsupported)throw std::runtime_error("Unsupported source standalone scene/phase boundary "+std::to_string(unsigned(scheduler.unsupported)));
  if(arcade.active)arcade.after_normal();
 }
 void irq_write(std::uint32_t address,std::uint32_t value,unsigned width){io.push_back({address,value,width});}
 void object_buffer_write(std::uint32_t address,std::uint32_t value){irq_write(address,value,4);game.sprite_frame={};game.board_objects.clear();game.pair_objects.clear();}
 void irq_video_input(){game.input_output=sample_input_ports(Game::globals,game.memory,ports);for(unsigned n=0;n<game.input_output.count;++n)irq_write(game.input_output.writes[n].address,game.input_output.writes[n].value,2);}
 bool irq_panel_service(){return !game.memory.read_byte(Game::globals+0xce);}
 void irq_sound_queue(){
  const auto output=dispatch_sound_request(Game::globals,game.memory,!device_ready||device_ready());
  if(set_stereo)set_stereo(output.writes[0].value);
  for(unsigned n=0;n<output.count;++n)irq_write(output.writes[n].address,output.writes[n].value,1);
  if(output.sent){++dispatched_sounds;if(deliver_sound)deliver_sound(output.command);}
 }
 void insert_coin(unsigned side=0){coin_inputs.coin(side);}
 void persist_operator_counter(){cabinet.operator_store.persist(Game::globals,game.memory);}
 void coin_sound(unsigned n){queue_sound_request(Game::globals,game.memory,{n,0x1000,0});}
 void request_credit_display(){cabinet.credit_refresh_pending=true;}
 bool irq_coin_accounting(){return source_coin_poll(Game::globals,game.memory,*this);}
 bool irq_coin_requests(){return source_coin_requests(Game::globals,game.memory,*this);}
 bool irq_wrap_statistics(){return !game.memory.read_byte(Game::globals+0x86);}
 bool main_accounting(){return !game.memory.read_byte(Game::globals+0x86);}
 void copy_inputs(){copy_player_inputs(Game::globals,game.memory);}
 bool overlay(){auto h=game.task_helpers();return tick_scene_overlay(Game::globals,game.memory,h);}
 bool request_interlude(){return request_source_interlude(Game::globals,game.memory);}
 void interlude_cue(){if(!game.memory.read_byte(Game::globals+0xa6)){queue_sound_request(Game::globals,game.memory,{0xff05,0,0});queue_sound_request(Game::globals,game.memory,{0x13,0,0},true);}}
 bool after_interlude_scene(){requested_scene=finish_source_interlude_scene(Game::globals,game.memory);scene_requested=!arcade.active;return true;}
 bool phase_after_clear(){return interlude.after_clear(Game::globals,game.memory,*this);}
 MatchPhaseFlow phase(MatchPhase phase){
  auto&m=game.memory;constexpr auto g=Game::globals;
  switch(phase){
  case MatchPhase::introduction:
   if(m.read_word(g+0x10)==2&&m.read_word(g+0x13e)==4)return interlude.step(g,m,*this);
   match.advance_intro(game);return MatchPhaseFlow::complete;
  case MatchPhase::playing:
   if(begin_match_play_phase(g,m)&&match.observe(game)){if(winner_event)winner_event();}
   return MatchPhaseFlow::complete;
  case MatchPhase::round_result:case MatchPhase::round_bonus:
   if(match.advance(game,arcade.active||!ending_complete||ending_complete())){if(new_round_event)new_round_event();}
   // The original next arcade scene is outside the standalone match. This
   // explicit host final-screen gate prevents extra gameplay after completion.
   if(match.post_complete){if(arcade.active){match.post_complete=false;arcade.match_over();}else m.write_byte(g+0x10f,1);}
   return MatchPhaseFlow::complete;
  }
  return MatchPhaseFlow::unsupported;
 }
 void turns(){game.tick({},false,true);}void stopped_turns(){game.stopped_work();}
 void reactions(){game.source_reactions();}void characters(){game.source_characters();}
 void first_pool(){game.source_first_pool();}void second_pool(){game.source_second_pool();}void third_pool(){game.source_third_pool();}
 void music_control(){source_music_tempo(Game::globals,game.memory);}void stage(){stage_frame(Game::globals,game.memory);}
 void gem_pictures(){build_gem_pictures(Game::globals,game.memory);}void object_packets(){game.source_packet_output();}
};
} // namespace puzzle_fighter::prototype
