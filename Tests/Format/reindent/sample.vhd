library ieee;
use ieee.std_logic_1164.all;
entity counter is
  generic (
    W : integer := 8
  );
  port (
    clk : in std_logic;
    q : out integer
  );
end entity;
architecture rtl of counter is
  signal n : integer := 0;
begin
  process (clk)
    variable v : integer;
  begin
    if rising_edge(clk) then
      if n = W then
        n <= 0;
      elsif n > W then
        n <= 1;
      else
        n <= n + 1;
      end if;
    end if;
    case n is
      when 0 =>
        v := 1;
      when others =>
        null;
    end case;
    for i in 0 to 3 loop
      v := v + i;
    end loop;
  end process;
  q <= n;
end architecture;
package p is
  function f(x : integer) return integer;
end package;
package body p is
  function f(x : integer) return integer is
  begin
    return x;
  end function;
end package body;
architecture beh of x is
  type rec is record
    a : integer;
  end record;
  component c is
    port (
      a : in bit
    );
  end component;
begin
  g : for i in 0 to 3 generate
    u : c port map (
      a => s
    );
  end generate;
  b : block
  begin
    s <= '1';
  end block;
end architecture;
