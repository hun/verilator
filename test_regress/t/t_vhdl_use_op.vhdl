-- DESCRIPTION: Verilator: VHDL a use clause naming an operator symbol
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

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
