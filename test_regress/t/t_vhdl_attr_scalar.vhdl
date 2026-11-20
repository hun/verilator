-- DESCRIPTION: Verilator: VHDL predefined scalar attributes
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- Every scalar type has the discrete attribute set (IEEE 1076-2008 16.2.2);
-- GHDL: bit'high is '1', bit'left is '0', std_logic'left is '0', and
-- std_logic'pos('1') is 1.

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    b1 : out bit;
    b2 : out bit;
    s1 : out std_logic;
    n  : out integer
  );
end;

architecture rtl of t is
begin
  b1 <= bit'high;
  b2 <= bit'left;
  s1 <= std_logic'left;
  n  <= std_logic'pos('1');
end;
