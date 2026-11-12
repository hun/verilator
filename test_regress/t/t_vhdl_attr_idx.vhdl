-- DESCRIPTION: Verilator: VHDL positional attributes used as an index/slice
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t_vhdl_attr_idx is
    port (
        i      : in  std_logic_vector(7 downto 0);
        o_hi   : out std_logic;  -- i(i'high) == i(7)
        o_lo   : out std_logic;  -- i(i'low)  == i(0)
        o_full : out std_logic_vector(7 downto 0);  -- i(i'high downto i'low) == i
        o_top  : out std_logic_vector(3 downto 0)  -- i(i'high downto 4) == i(7..4)
    );
end;

architecture a of t_vhdl_attr_idx is
begin
    o_hi   <= i(i'high);
    o_lo   <= i(i'low);
    o_full <= i(i'high downto i'low);
    o_top  <= i(i'high downto 4);
end;
