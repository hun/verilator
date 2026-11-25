-- DESCRIPTION: Verilator: VHDL interface view with an unconstrained element, from the NVC test view3
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- From nvc test/regress/view3.vhd; the record compares of its check process are left out
package pack is
    type rec_t is record
        x, y : natural;
        b : bit_vector;
    end record;

    view in_view of rec_t is
        x : in;
        y : out;
        b : in;
    end view;

    alias out_view is in_view'converse;
end package;

-------------------------------------------------------------------------------

use work.pack.all;

entity sub is
    port ( i : view in_view; o : view out_view );
end entity;

architecture test of sub is
begin

    o.x <= i.x + 1;
    i.y <= o.y + 5;
    o.b <= not i.b;

end architecture;

-------------------------------------------------------------------------------

use work.pack.all;

entity sub2 is
    port ( i : view in_view; o : view out_view );
end entity;

architecture test of sub2 is
begin

    u: entity work.sub port map ( i, o );

end architecture;

-------------------------------------------------------------------------------

use work.pack.all;

entity view3 is
end entity;

architecture test of view3 is
    signal i, o : rec_t(b(1 to 3));
begin

    u: entity work.sub port map (i, o);

    check: process is
    begin
        wait for 0 ns;
        i.x <= 7;
        o.y <= 12;
        i.b <= "101";
        wait for 1 ns;
        wait;
    end process;

end architecture;
