-- DESCRIPTION: Verilator: VHDL subprogram bodies are unique
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity t is
  port (y : out integer);
end;

architecture rtl of t is
  function f(x : integer) return integer is
  begin
    return x;
  end function f;
  function f(x : integer) return integer is
  begin
    return x + 1;
  end function f;
begin
  y <= f(1);
end;
