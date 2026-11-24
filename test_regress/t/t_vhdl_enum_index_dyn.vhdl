-- DESCRIPTION: Verilator: an array indexed by an enumeration value at run time
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
entity t_vhdl_enum_index_dyn is
  port (a : in integer range 0 to 3; o : out std_logic_vector(7 downto 0);
        o2 : out std_logic);
end;
architecture r of t_vhdl_enum_index_dyn is
  type st_t is (S0, S1, S2, S3);
  type tab_t is array (st_t) of std_logic_vector(7 downto 0);
  constant tab : tab_t := (x"11", x"22", x"33", x"44");
  type rec_t is record state : st_t; end record;
  signal r : rec_t;
  signal s : st_t;
begin
  s <= st_t'val(a);
  r.state <= s;
  o <= tab(s);
  o2 <= tab(r.state)(0);
end;
