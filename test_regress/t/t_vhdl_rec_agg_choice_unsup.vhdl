-- DESCRIPTION: Verilator: VHDL record aggregate with a literal choice is unsupported
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A record association must name an element; GHDL rejects the literal choice below.

entity t is
  port (clk : in bit; o : out bit);
end entity t;

architecture rtl of t is
  signal sb : bit;
  type rec is record
    a : bit;
    b : bit;
  end record;
  signal r : rec;
begin
  r <= (1 => '1', b => '0');
  o <= r.a;
end architecture rtl;
