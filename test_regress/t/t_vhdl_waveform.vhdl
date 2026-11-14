-- DESCRIPTION: Verilator: a concurrent waveform list
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
end entity t;

architecture sim of t is
  signal sig : std_logic := '0';
begin
  -- A waveform list: two elements, each with its own time (IEEE 1800-2023 10.6)
  sig <= '0', '1' after 5 ns;

  early : process
  begin
    wait for 2 ns;
    if sig = '0' then
      report "sig is low at 2 ns";
    else
      report "sig is high at 2 ns";
    end if;
    wait;
  end process early;

  late : process
  begin
    wait for 7 ns;
    if sig = '1' then
      report "sig is high at 7 ns";
    else
      report "sig is low at 7 ns";
    end if;
    std.env.finish;
    wait;
  end process late;
end architecture sim;