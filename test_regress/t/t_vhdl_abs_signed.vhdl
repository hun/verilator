-- DESCRIPTION: Verilator: VHDL abs on a numeric_std signed vector
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
    s  : in  signed(7 downto 0);
    a1 : out signed(7 downto 0)
  );
end;

architecture rtl of t is
begin
  -- numeric_std defines "abs" for a signed operand (IEEE 1076-2008 Annex C).
  -- GHDL: abs(-2) = 2.
  a1 <= abs(s);
end;
