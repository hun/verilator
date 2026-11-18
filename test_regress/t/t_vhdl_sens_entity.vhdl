-- DESCRIPTION: Verilator: VHDL process(all) is sensitive to an entity-declared signal
--
-- This file ONLY is placed in the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    a : in  std_logic;
    y : out std_logic
  );
  -- A signal declared beside the entity's ports is a signal of the design
  -- (IEEE 1076-2008 6.2), and process(all) reads it.
  signal s : std_logic := '0';
end;

architecture rtl of t is
begin
  -- The s event re-triggers the process, so y catches up to a within the
  -- same simulation cycle. GHDL: y is '1' one delta after a goes high.
  proc : process (all)
  begin
    s <= a;
    y <= s;
  end process;
end;
