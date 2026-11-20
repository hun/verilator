-- DESCRIPTION: Verilator: VHDL a qualified array index subtype mark
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

package qp2 is
  subtype idx is integer range 0 to 3;
end package;

use work.qp2.all;

entity tb is
end;

architecture sim of tb is
  type arr_t is array (work.qp2.idx) of integer;
  signal a : arr_t;
begin
  process
  begin
    assert a'length = 4 report "qualified index range not used" severity failure;
    std.env.finish;
    wait;
  end process;
end;
