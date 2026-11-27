-- DESCRIPTION: Verilator: An exit statement has to sit inside a loop of its own subprogram
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity t is
  port (a : in bit_vector(3 downto 0); y : out bit_vector(3 downto 0));
end;

architecture rtl of t is
  procedure p(x : in bit_vector(3 downto 0); r : out bit_vector(3 downto 0)) is
  begin
    r := x;
    exit;
  end procedure;
begin
  process (a)
    variable v : bit_vector(3 downto 0);
  begin
    for i in 0 to 1 loop
      p(a, v);
    end loop;
    y <= v;
  end process;
end;
