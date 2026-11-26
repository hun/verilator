-- DESCRIPTION: Verilator: VHDL function assigns to a field of an in parameter
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
entity t is
  port (o : out integer);
end entity t;

architecture rtl of t is
  type rec is record
    a : integer;
    b : integer;
  end record;
  constant c : rec := (a => 1, b => 2);
  function f(r : rec) return integer is
  begin
    r.a := 5;
    return r.a + r.b;
  end function f;
begin
  o <= f(c);
end architecture rtl;
