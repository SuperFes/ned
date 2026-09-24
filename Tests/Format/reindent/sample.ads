package Shapes is
   type Kind is (Square, Circle);
   type Rect is record
      W, H : Integer;
   end record;
   procedure Show;
   function Area (R : Rect) return Integer;
private
   Default : constant Rect := (1, 1);
end Shapes;
