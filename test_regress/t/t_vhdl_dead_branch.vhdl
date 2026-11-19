-- DESCRIPTION: Verilator: a null slice in a branch a static condition disables
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- poc's fifo_cc_got slices the pointer difference to the number of state bits, so with
-- zero state bits the slice is null: `d(d'left downto d'left - EMPTY_STATE_BITS + 1)`.
-- A null slice is legal, and the branch that holds it never runs, so it is not lowered.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  generic (
    EMPTY_STATE_BITS : natural := 0
  );
  port (
    clk : in  std_logic;
    d   : in  std_logic_vector(7 downto 0);
    o   : out std_logic_vector(7 downto 0)
  );
end entity;

architecture rtl of t is
begin
  process (clk)
  begin
    if rising_edge(clk) then
      o <= d;
      -- Null while EMPTY_STATE_BITS is zero, which is what the static condition says
      if EMPTY_STATE_BITS > 0 then
        o <= not d(d'left downto d'left - EMPTY_STATE_BITS + 1)
             & d(d'left - EMPTY_STATE_BITS downto 0);
      end if;
    end if;
  end process;
end architecture;
