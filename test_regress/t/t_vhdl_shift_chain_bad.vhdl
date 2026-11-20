-- DESCRIPTION: Verilator: VHDL shift operators cannot be chained
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    a : in  std_logic_vector(3 downto 0);
    y : out std_logic_vector(3 downto 0)
  );
end;

architecture rtl of t is
begin
  -- IEEE 1076-2008 9.1: shift operators are non-associative
  y <= a sll 1 sll 2;
end;
