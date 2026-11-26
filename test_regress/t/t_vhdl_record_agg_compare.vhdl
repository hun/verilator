-- DESCRIPTION: Verilator: a record compared with an aggregate
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity t is
  port (a  : in bit_vector(5 downto 0);
        b  : in bit_vector(1 downto 0);
        e1 : out bit;
        e2 : out bit;
        e3 : out bit);
end entity t;

architecture rtl of t is
  type rec is record
    x : bit_vector(5 downto 0);
    y : bit_vector(1 downto 0);
  end record;
  signal r : rec;
begin
  r.x <= a;
  r.y <= b;
  e1 <= '1' when r = (x => "101000", y => "01") else '0';
  e2 <= '1' when (x => "101000", y => "01") = r else '0';
  e3 <= '1' when r /= (x => "101000", y => "01") else '0';
end architecture rtl;
