module m #(parameter W = 8) (
  input clk,
  output reg [W-1:0] q
);
  always @(posedge clk) begin
    if (rst) begin
      q <= 0;
    end else begin
      q <= q + 1;
    end
  end
  case (s)
    0: x = 1;
    default: x = 2;
  endcase
  function integer f;
    input a;
    begin
      f = a;
    end
  endfunction
  task automatic pulse(input integer n);
    repeat (n) begin
      @(posedge clk);
    end
  endtask
endmodule
