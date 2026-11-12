-- DESCRIPTION: Verilator: VHDL for-generate over a signal's 'range attribute
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    d : in  std_logic_vector(3 downto 0);
    q : out std_logic_vector(3 downto 0);
    r : out std_logic_vector(3 downto 0)
  );
end t;

architecture behav of t is
begin
  -- `for i in obj'range generate` iterates the prefix's own bounds, taken from its type
  gen: for i in d'range generate
    q(i) <= not d(i);
  end generate;

  gen2: for i in q'range generate
    r(i) <= d(3 - i);
  end generate;
end behav;
