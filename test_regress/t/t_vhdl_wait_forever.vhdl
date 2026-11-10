-- DESCRIPTION: Verilator: VHDL process that suspends forever with `wait;`
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    clk : in  std_logic;
    q   : out integer;
    r   : out integer
  );
end entity t;

architecture rtl of t is
  signal one_shot : integer := 0;
  signal ticks    : integer := 0;
begin

  -- A bare wait suspends the process forever: the statements before it run once
  -- at time zero and are never reached again.
  p : process
  begin
    one_shot <= 7;
    report "one-shot process";
    wait;
  end process;

  -- Proves the one-shot process is not restarted by clock activity
  c : process (clk)
  begin
    if rising_edge(clk) then
      ticks <= ticks + 1;
    end if;
  end process;

  q <= one_shot;
  r <= ticks;

end architecture rtl;
