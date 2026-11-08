-- DESCRIPTION: Verilator: VHDL assert and report statements through GHDL XML
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

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
begin
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
