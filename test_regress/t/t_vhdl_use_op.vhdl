-- DESCRIPTION: Verilator: VHDL a use clause naming an operator symbol
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

package opkg is
  function "+"(a, b : integer) return integer;
end package;

package body opkg is
  function "+"(a, b : integer) return integer is
  begin
    return a - b;
  end function;
end package body;

use work.opkg."+";

entity tb is
end;

architecture sim of tb is
  signal y : integer := 0;
begin
  y <= 1 + 2;
  process
  begin
    wait for 1 ns;
    assert y = -1 report "operator symbol not imported" severity failure;
    std.env.finish;
    wait;
  end process;
end;
