-- DESCRIPTION: Verilator: an alias of a signal in a sensitivity list and an edge call
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    clk : in std_logic;
    d   : in std_logic;
    q   : out std_logic
  );
end entity t;

architecture rtl of t is
  -- The process is sensitive to the alias and the register is clocked by the edge
  -- function on it, both of which denote clk
  alias fast_clk : std_logic is clk;
begin
  process (fast_clk) is
  begin
    if rising_edge(fast_clk) then
      q <= d;
    end if;
  end process;
end architecture rtl;