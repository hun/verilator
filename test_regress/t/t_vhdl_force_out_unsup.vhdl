-- DESCRIPTION: Verilator: force of the driving value is refused
library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    en : in std_logic;
    q  : out std_logic
  );
end entity;

architecture rtl of t is
  signal s : std_logic;
begin
  process (en) begin
    s <= force out '1';
  end process;
  q <= s;
end architecture;
