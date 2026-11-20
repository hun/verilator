-- DESCRIPTION: Verilator: VHDL slices with a dynamic offset but a static width
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    n  : in  integer;                        -- dynamic element index
    d  : in  std_logic_vector(15 downto 0);
    q4 : out std_logic_vector(3 downto 0);
    q8 : out std_logic_vector(7 downto 0)
  );
end t;

architecture behav of t is
begin
  -- A dynamic slice position whose width is still static: both bounds are linear in n
  -- with the same coefficient, so the element count is a constant (4 and 8). The lower
  -- bound (n+1)*8-8 only reduces to n*8 by distributing the multiply.
  q4 <= d(n*4+3 downto n*4);
  q8 <= d(n*8+7 downto (n+1)*8 - 8);
end behav;
