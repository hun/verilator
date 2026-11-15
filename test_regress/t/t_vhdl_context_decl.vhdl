-- DESCRIPTION: Verilator: VHDL a context declaration beside design units
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A context declaration bundles library and use clauses under a name, and poc's
-- common.vhdl is exactly that: clauses and no design unit of its own. The frontend
-- has no use for the bundle, but the units after it still have to parse.

library ieee;
use ieee.std_logic_1164.all;

context unused_context is
  library ieee;
  use ieee.std_logic_1164.all;
  use ieee.numeric_std.all;
end context;

package ctxpkg is
  function twice (n : natural) return natural;
end package;

package body ctxpkg is
  function twice (n : natural) return natural is
  begin
    return 2 * n;
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use work.ctxpkg.all;

entity t_vhdl_context_decl is
  port (
    o : out integer
  );
end entity;

architecture rtl of t_vhdl_context_decl is
begin
  o <= twice(21);
end architecture;