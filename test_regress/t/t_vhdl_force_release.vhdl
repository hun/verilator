library ieee;
use ieee.std_logic_1164.all;

entity t_vhdl_force_release is
  port (
    en : in std_logic;
    d  : in std_logic_vector(3 downto 0);
    q  : out std_logic_vector(3 downto 0);
    q2 : out std_logic_vector(3 downto 0)
  );
end entity;

architecture rtl of t_vhdl_force_release is
  signal s : std_logic_vector(3 downto 0);
  signal s2 : std_logic_vector(3 downto 0);
begin
  s <= d;
  process (en) begin
    if en = '1' then
      s <= force "1010";
    else
      s <= release;
    end if;
  end process;
  q <= s;
  s2 <= d;
  process (en) begin
    if en = '1' then
      s2 <= force out "0110";
    else
      s2 <= release out;
    end if;
  end process;
  q2 <= s2;
end architecture;
