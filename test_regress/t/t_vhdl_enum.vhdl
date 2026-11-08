-- DESCRIPTION: Verilator: VHDL enumeration types through GHDL XML
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

package t_pkg is
  type state_t is (idle, run, done);
end;

library ieee;
use ieee.std_logic_1164.all;
use work.t_pkg.all;

entity t is
  port (
    clk     : in std_logic;
    q       : out std_logic;
    state_o : out state_t
  );
end;

architecture rtl of t is
  signal st : state_t;
begin
  process (clk) is
  begin
    if rising_edge(clk) then
      case st is
        when idle => st <= run;
        when run => st <= done;
        when others => st <= idle;
      end case;
    end if;
  end process;

  q <= '1' when st = idle else '0';
  state_o <= st;
end;
