-- DESCRIPTION: Verilator: a conditional signal assignment whose value reads a clock edge is refused
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A value that calls rising_edge is not a register assignment; the edge has no clock process to sit in.
library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    clk, s : in std_logic;
    y : out std_logic
  );
end entity t;

architecture rtl of t is
  function b2s(b : boolean) return bit is
  begin
    if b then return '1'; end if;
    return '0';
  end function;
begin
  y <= b2s(rising_edge(clk)) when s = '1' else '0';
end architecture rtl;
