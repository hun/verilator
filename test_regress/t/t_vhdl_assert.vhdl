-- DESCRIPTION: Verilator: VHDL assert and report statements through the native VHDL frontend
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee; use ieee.std_logic_1164.all;
package ap is
  function clamp1(x : integer) return integer;
end;
package body ap is
  function clamp1(x : integer) return integer is
  begin
    assert x >= 0 report "negative" severity warning;
    if x > 3 then
      return 3;
    end if;
    return x;
  end;
end;
library ieee; use ieee.std_logic_1164.all; use work.ap.all;
entity t is port (clk : in std_logic; din : in std_logic; a : in integer;
                  y : out std_logic; v : out integer); end;
architecture rtl of t is
  procedure check(arg : in std_logic) is
  begin
    assert arg = '0' or arg = '1' report "bad arg" severity warning;
  end procedure;
begin
  check(din);
  process (clk) is
  begin
    if rising_edge(clk) then
      assert din /= 'X' report "bad din" severity error;
      report "tick";
      y <= din;
    end if;
  end process;
  v <= clamp1(a);
end;
