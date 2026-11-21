-- DESCRIPTION: Verilator: VHDL ambiguous unary operator diagnostic
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- Two unary operator overloads that differ only in their result are ambiguous without a context.

package p is
  type t is range 0 to 7;
  type u is range 0 to 7;
  function "-" (a : t) return t;
  function "-" (a : t) return u;
end package;

package body p is
  function "-" (a : t) return t is
  begin
    return a;
  end function;
  function "-" (a : t) return u is
  begin
    return 0;
  end function;
end package body;

use work.p.all;

entity top is
  port (o : out bit);
end entity;

architecture rtl of top is
  constant c : t := 3;
begin
  o <= '1' when -c = -c else '0';
end architecture;
