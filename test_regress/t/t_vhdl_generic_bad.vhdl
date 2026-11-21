-- DESCRIPTION: Verilator: VHDL generic diagnostic
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- Generics without a value or association, bad generic maps and component generic conformance.

package gp is
  generic (g : integer; h : integer);
end package;

package pi is new work.gp generic map (g => 3);

package gq is
  generic (g : integer := 1);
end package;

package qi is new work.gq generic map (g => open);

entity sub is
  generic (n : integer);
end entity;

architecture rtl of sub is
begin
end architecture;

entity leaf is
end entity;

architecture rtl of leaf is
begin
end architecture;

entity top is
end entity;

architecture rtl of top is
  component sub is
    generic (n : bit := '0');
  end component;
begin
  u1 : entity work.sub;
  u2 : entity work.sub generic map (n => open);
  u3 : entity work.leaf generic map (n => 1);
  u4 : sub generic map (n => '1');
end architecture;
