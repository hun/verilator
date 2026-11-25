-- DESCRIPTION: Verilator: VHDL subtype bounds taken from attributes of a port
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t_vhdl_subtype_port_attr is
  port (foo : in  std_logic_vector(9 downto 2);
        y1  : out std_logic_vector(4 downto 0);
        y2  : out std_logic_vector(7 downto 0);
        y3  : out std_logic_vector(7 downto 0));
end t_vhdl_subtype_port_attr;

architecture behav of t_vhdl_subtype_port_attr is
  -- The port is declared before the architecture, so its bounds are known here
  subtype upper_t is natural range foo'high downto foo'low + 3;
  subtype whole_t is natural range foo'left downto foo'right;
  subtype index_t is natural range 0 to foo'length - 1;
begin
  y1 <= foo (upper_t);
  y2 <= foo (whole_t);
  y3 <= std_logic_vector (to_unsigned (index_t'high, 8));
end behav;
