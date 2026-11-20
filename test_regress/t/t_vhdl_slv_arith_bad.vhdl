-- DESCRIPTION: Verilator: VHDL std_logic_vector arithmetic needs a package
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The predefined arithmetic operators have bit and boolean elements (IEEE
-- 1076-2008 9.1); a std_logic_vector needs std_logic_unsigned or
-- std_logic_arith visible, so GHDL refuses this design.

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    a : in  std_logic_vector(3 downto 0);
    b : in  std_logic_vector(3 downto 0);
    y : out std_logic_vector(3 downto 0)
  );
end;

architecture rtl of t is
begin
  y <= a + b;
end;
