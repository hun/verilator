-- DESCRIPTION: Verilator: find_leftmost and find_rightmost
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (
    d    : in  unsigned(7 downto 0);
    a    : in  signed(8 to 12);
    dl   : out integer;
    dr   : out integer;
    d0   : out integer;
    al   : out integer;
    ar   : out integer
  );
end entity t;

architecture rtl of t is
begin
  dl <= find_leftmost(d, '1');
  dr <= find_rightmost(d, '1');
  d0 <= find_leftmost(d, '0');
  al <= find_leftmost(a, '1');
  ar <= find_rightmost(a, '1');
end architecture rtl;
