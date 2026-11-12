-- DESCRIPTION: Verilator: VHDL character and extended enumeration literals
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t_vhdl_enum_lit is
    port (
        sel   : in  std_logic_vector(1 downto 0);
        pos_o : out integer
    );
end;

architecture a of t_vhdl_enum_lit is
    type my_enum is (normal, 'z', \ext\, \long\\ext\);
begin
    process (sel)
        variable v : my_enum;
    begin
        case sel is
            when "00"   => v := normal;
            when "01"   => v := 'z';
            when "10"   => v := \ext\;
            when others => v := \long\\ext\;
        end case;
        pos_o <= my_enum'pos(v);
    end process;
end;
