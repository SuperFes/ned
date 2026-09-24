unit Shapes;

interface

uses
  SysUtils, Classes;

type
  TPoint = record
    X, Y: Integer;
  end;

  TShape = class(TObject)
  private
    FName: string;
  public
    constructor Create(const AName: string);
    function Area: Double; virtual;
  end;

const
  Pi2 = 6.28;

implementation

constructor TShape.Create(const AName: string);
begin
  FName := AName;
end;

function TShape.Area: Double;
var
  i: Integer;
begin
  Result := 0;
  for i := 1 to 3 do
    Result := Result + i;
  while Result > 10 do
  begin
    Result := Result - 1;
  end;
  if Result > 1 then
    WriteLn('big')
  else if Result > 0 then
    WriteLn('small')
  else
  begin
    WriteLn('none');
  end;
  case i of
    1: WriteLn('one');
    2:
      WriteLn('two');
  end;
  repeat
    Dec(i);
  until i = 0;
  try
    WriteLn(i);
  except
    on E: Exception do
      WriteLn(E.Message);
  end;
end;

end.
