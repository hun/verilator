-- DESCRIPTION: Verilator: VHDL array attribute indexed by an integer constant
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
entity t is
  port (o : out integer);
end entity t;

architecture rtl of t is
  type m2 is array (0 to 1, 0 to 2) of bit;
  constant two : integer := 2;
  signal m : m2;
begin
  o <= m'length(two);
end architecture rtl;
