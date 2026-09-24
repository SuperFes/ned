with Ada.Text_IO;
package body Shapes is
   type Rect is record
      W : Integer;
      H : Integer;
   end record;
   function Area (R : Rect) return Integer is
      Result : Integer := 0;
   begin
      if R.W < 0 then
         raise Constraint_Error;
      elsif R.W = 0 then
         Result := 0;
      else
         Result := R.W * R.H;
      end if;
      case R.H is
         when 0 =>
            null;
         when others =>
            Result := Result + 1;
      end case;
      for I in 1 .. 3 loop
         Result := Result + I;
      end loop;
      while Result > 100 loop
         Result := Result - 1;
      end loop;
      declare
         X : Integer := 1;
      begin
         Result := X;
      exception
         when others =>
            null;
      end;
      return Result;
   end Area;
   procedure Show is
   begin
      Ada.Text_IO.Put_Line ("x");
   exception
      when E : others =>
         Ada.Text_IO.Put_Line ("failed");
         raise;
   end Show;
   task type Worker is
      entry Start;
   private
      entry Stop;
   end Worker;
   protected type Counter is
      procedure Inc;
   private
      N : Integer := 0;
   end Counter;
   task body Worker is
   begin
      select
         accept Start do
            null;
         end Start;
      or
         terminate;
      end select;
   end Worker;
   function Make return Rect is
   begin
      return R : Rect do
         R.W := 1;
      end return;
   end Make;
   type V (D : Boolean) is record
      case D is
         when True =>
            X : Integer;
         when False =>
            null;
      end case;
   end record;
end Shapes;
