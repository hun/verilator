-- DESCRIPTION: Verilator: VHDL duplicate package diagnostic
--
-- This file ONLY is placed in the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- Two packages of one name in the design library would silently overwrite each
-- other, and the value of `k` would depend on file order. The frontend reports
-- the second unit instead.

package p is
  constant k : integer := 1;
end package;

package p is
  constant k : integer := 2;
end package;

entity t is
  port (
    y : out integer
  );
end entity;

architecture rtl of t is
  use work.p.all;
begin
  y <= k;
end architecture;
