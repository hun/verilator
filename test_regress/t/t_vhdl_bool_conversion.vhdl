-- DESCRIPTION: Verilator: a boolean conversion of a comparison beside a boolean
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t_vhdl_bool_conversion is
  generic (EN : boolean := true; N : natural := 2);
  port (a : in std_logic; o1 : out std_logic; o2 : out std_logic);
end;

architecture r of t_vhdl_bool_conversion is
  constant c1 : boolean := EN and boolean(N > 0);
  constant c2 : boolean := boolean(N >= 8) or EN;
  constant c3 : boolean := c1 and not boolean(N >= 8);
begin
  o1 <= a when c3 else '0';
  o2 <= a when c2 and boolean(N < 2) else '1';
end;
