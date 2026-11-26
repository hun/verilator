-- DESCRIPTION: Verilator: VHDL function assigns to an in parameter
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
entity t is
  port (a : in integer; o : out integer);
end entity t;

architecture rtl of t is
  function f(v : integer) return integer is
  begin
    v := 5;
    return v;
  end function f;
begin
  o <= f(a);
end architecture rtl;
