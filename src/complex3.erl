-module(complex3).
-export([foo/1, bar/1, test/0, srv/0]).

foo(X) ->
    call_cnode({foo, X}).
bar(Y) ->
    call_cnode({bar, Y}).

call_cnode(Msg) ->
    %%{ok, Hostname} = inet:gethostname(),
    Hostname = "localhost",
    {any, list_to_atom(lists:append(["c1@", Hostname]))} ! {call, self(), Msg},
    receive
    {cnode, Result} ->
        Result
    end.

%% C node invoking erlang functions

efoo(Num) ->
    io:format("Called efoo~p~n", [Num]),
    Num + 2.

srv() ->
    receive
        {cnode, From , {efoo, Arg}} ->
            From ! {ok, efoo(Arg)};
        {cnode, From, Result} ->
            io:format("~p~n", [Result]),
            From ! ok
    end,
    srv().

test() ->
    Pid = spawn(?MODULE, srv, []),
    link(Pid),
    register(test, Pid),
    ok.
