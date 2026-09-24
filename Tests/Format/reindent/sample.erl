-module(shapes).
-export([classify/1, total/1]).

-record(point, {x = 0, y = 0}).

classify(N) when is_integer(N) ->
    case N of
        0 ->
            zero;
        _ when N > 0 ->
            positive;
        _ ->
            negative
    end.

total(Xs) ->
    lists:foldl(
        fun(X, Acc) ->
            X + Acc
        end,
        0,
        Xs
    ).

safe(F) ->
    try F() of
        Value ->
            {ok, Value}
    catch
        error:Reason ->
            {error, Reason}
    end.

wait() ->
    receive
        {msg, M} ->
            M
    after 1000 ->
        timeout
    end.

check(X) ->
    if
        X > 0 ->
            pos;
        true ->
            other
    end.
