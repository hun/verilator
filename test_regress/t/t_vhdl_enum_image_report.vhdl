library ieee;
use ieee.std_logic_1164.all;

entity t_vhdl_enum_image_report is
  port (
    clk : in std_logic;
    s   : in integer range 0 to 2;
    o   : out std_logic
  );
end entity;

architecture rtl of t_vhdl_enum_image_report is
  type st_t is (idle, run, done);
  signal st : st_t := idle;
begin
  process (clk) begin
    if rising_edge(clk) then
      st <= st_t'val(s);
      report "state " & st_t'image(st);
    end if;
  end process;
  o <= '1' when st = done else '0';
end architecture;
