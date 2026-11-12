-- DESCRIPTION: Verilator: VHDL 'event on a signal element
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- clk(0) is the clock and only bit 0 is a clock; toggling bit 1 while bit 0
-- holds level must not latch, which an event-on-the-whole-vector model gets
-- wrong. q takes d only on a rising edge of clk(0).

library ieee;
use ieee.std_logic_1164.all;

entity t_vhdl_evt is
  port (
    clk : in  std_logic_vector(1 downto 0);
    d   : in  std_logic;
    q   : out std_logic
  );
end entity;

architecture a of t_vhdl_evt is
begin
  process (clk)
  begin
    if clk(0)'event and clk(0) = '1' then
      q <= d;
    end if;
  end process;
end architecture;
