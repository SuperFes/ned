architecture rtl of e is
begin
  with sel select
    y <= a when "00",
    b when "01",
    c when others;
  z <= a when s = '1' else
    b;
  process (clk)
  begin
    q <= a and
      b;
  end process;
end architecture rtl;
