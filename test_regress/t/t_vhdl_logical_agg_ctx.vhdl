-- DESCRIPTION: Verilator: aggregate operands of logical operators take the vector type from context
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
end entity;

architecture test of t is
begin
  process
    variable a : std_logic_vector(3 downto 0);
    variable r : std_logic_vector(3 downto 0);
    variable s : std_logic;
    variable errors : integer := 0;
  begin
    a := "1010";
    s := '1';
    r := a and not (a'range => s);
    if r /= "0000" then report "a and not (a'range => s)"; errors := errors + 1; end if;
    s := '0';
    r := a and not (a'range => s);
    if r /= "1010" then report "a and not (a'range => s) with s low"; errors := errors + 1; end if;
    s := '1';
    r := (a'range => s) and a;
    if r /= "1010" then report "(a'range => s) and a"; errors := errors + 1; end if;
    s := '0';
    r := (a'range => s) and a;
    if r /= "0000" then report "(a'range => s) and a with s low"; errors := errors + 1; end if;
    s := '1';
    r := not (a'range => s) and a;
    if r /= "0000" then report "not (a'range => s) and a"; errors := errors + 1; end if;
    s := '0';
    r := not (a'range => s) and a;
    if r /= "1010" then report "not (a'range => s) and a with s low"; errors := errors + 1; end if;
    s := '1';
    r := not (a'range => s);
    if r /= "0000" then report "not (a'range => s)"; errors := errors + 1; end if;
    s := '0';
    r := not (a'range => s);
    if r /= "1111" then report "not (a'range => s) with s low"; errors := errors + 1; end if;
    if errors = 0 then report "all logical aggregate operands match"; end if;
    wait;
  end process;
end architecture;
