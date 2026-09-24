function y = f(x)
if x > 0
    y = 1;
elseif x < 0
    y = -1;
else
    y = 0;
end
for i = 1:3
    disp(i);
end
switch x
    case 1
        disp('one');
    otherwise
        disp('other');
end
try
    g();
catch err
    disp(err);
end
while x > 0
    x = x - 1;
end
end
classdef P < handle
    properties
        X
    end
    methods
        function obj = P(x)
            obj.X = x;
        end
    end
end
