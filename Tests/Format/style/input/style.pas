program P;
procedure A(x: Integer);
begin
  if x > 1 then begin
    x := 1;
  end
  else begin
    x := 2;
  end;
  while x > 0 do begin
    x := x - 1;
  end;
end;
begin
end.
