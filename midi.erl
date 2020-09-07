-module(midi). 
-export([start/0,start/1,init/0,midi_loop/1,looper_loop/3,send_after_loop/1]). 

% LoopLength is the number of quarter notes in the loop
start() -> start(16).
start(LoopLength) ->
  erlang:display("spawn"),
  spawn_link(midi, init, []),
  register(send_after, spawn(midi, send_after_loop, [lifo:new()])),
  register(looper, spawn(midi, looper_loop, [LoopLength,0,0])).

init() ->
  register(midi, self()),
  process_flag(trap_exit, true),
  Port = open_port({spawn, "./coremidi"}, [{packet, 2}]),
  midi_loop(Port).

send_after_loop(Msgs) ->
   receive
       {Delay, Msg} -> 
            erlang:send_after(Delay, send_after, tick),
            send_after_loop(lifo:append({Delay, Msg},Msgs));
       tick ->
            {Delay, Msg} = lifo:peek(Msgs), 
            midi ! {call, self(), Msg},
            send_after ! {Delay, Msg},
            send_after_loop(lifo:next(Msgs))
    end.
 
midi_loop(Port) ->
  receive
      {call, _, Msg} ->
	  Port ! {self(), {command, Msg}};
      {Port, {data, [248]}} ->
          looper ! tick;
      {Port, {data, Data}} ->
	  erlang:display(decode(Data)),
          looper ! {call, Data}
  end,
  midi_loop(Port).

looper_loop(LoopLength, LastTick, Delta) ->
  receive
     tick -> if LastTick == 0 ->
		looper_loop(LoopLength, erlang:timestamp(), 0);
	     true ->
                Now = erlang:timestamp(),
		looper_loop(LoopLength, Now, timer:now_diff(Now, LastTick))
             end;
     {call, Msg} ->
         PlayTime = floor(Delta/1000) * (24*LoopLength),
         send_after ! {PlayTime, Msg},
         looper_loop(LoopLength, LastTick, Delta)
  end.

decode(A) -> lists:flatten(io_lib:format("~p",[A])).
