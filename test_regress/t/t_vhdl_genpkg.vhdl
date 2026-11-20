-- DESCRIPTION: Verilator: VHDL generic-package instantiation
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

package genpkg is
    generic (add : integer; max : natural);
    subtype small_t is integer range 0 to max;  -- Range resolved from a generic
    constant ceiling : integer := max;  -- Constant derived from a generic
    function bump (x : integer) return integer;  -- Body reads a generic
end;

package body genpkg is
    function bump (x : integer) return integer is
    begin
        return x + add;
    end;
end;

entity t_vhdl_genpkg is
    port (
        i  : in  integer;
        o1 : out integer;
        o2 : out integer
    );
end;

architecture a of t_vhdl_genpkg is
    package inst5 is new work.genpkg generic map (add => 5, max => 100);
    use inst5.all;
begin
    o1 <= bump(i);  -- i + add = i + 5
    o2 <= bump(ceiling);  -- max + add = 100 + 5
end;
