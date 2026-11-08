-- DESCRIPTION: Verilator: VHDL enumeration types and attributes through GHDL XML
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

package t_pkg is
  type state_t is (idle, run, done);
  constant run_pred  : state_t := state_t'pred(done);
  constant run_succ  : state_t := state_t'succ(idle);
  constant high_pos  : integer := state_t'pos(state_t'high);
end;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.t_pkg.all;

entity t is
  port (
    clk         : in std_logic;
    sel         : in std_logic_vector(1 downto 0);
    q           : out std_logic;
    state_o     : out state_t;
    succ_o      : out state_t;
    pos_o       : out std_logic_vector(1 downto 0);
    high_o      : out std_logic;
    low_o       : out std_logic;
    const_pos_o : out std_logic_vector(2 downto 0);
    val_o       : out state_t
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
  succ_o <= state_t'succ(st);
  pos_o <= std_logic_vector(to_unsigned(state_t'pos(st), 2));
  high_o <= '1' when st = state_t'high else '0';
  low_o <= '1' when st = state_t'low else '0';
  const_pos_o
    <= std_logic_vector(to_unsigned(state_t'pos(run_pred) + state_t'pos(run_succ) + high_pos,
                                    3));
  val_o <= state_t'val(to_integer(unsigned(sel)));
end;
