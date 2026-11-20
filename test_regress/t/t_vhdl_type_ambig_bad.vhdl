-- DESCRIPTION: Verilator: VHDL type name imported from two packages
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

package a_pkg is
  type pair_t is array (0 to 1) of integer;
end package;

package b_pkg is
  type pair_t is array (0 to 1) of integer;
end package;

library work;
use work.a_pkg.all;
use work.b_pkg.all;

entity t is
  port (o : out integer);
end;

architecture rtl of t is
  signal x : pair_t;
begin
  o <= 0;
end;
