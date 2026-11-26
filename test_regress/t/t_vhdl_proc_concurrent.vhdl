-- DESCRIPTION: Verilator: a concurrent procedure drives signal out parameters as one process
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A concurrent procedure call is one combinational process: its locals are
-- intermediates and its signal out parameters are the process outputs, so the
-- whole expansion is one driver set (IEEE 1076-2008 11.4).

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (
    a, b : in  std_logic_vector(7 downto 0);
    sub  : in  std_logic;
    cin  : in  std_logic;
    q    : out std_logic_vector(7 downto 0);
    c    : out std_logic
  );
end entity;

architecture rtl of t is
  procedure addsub(A, B   : std_logic_vector;
                   Sub, Cin : std_logic;
                   signal Res  : out std_logic_vector;
                   signal Cout : out std_logic) is
    variable B_i : unsigned(A'length - 1 downto 0);
    variable R_i : unsigned(A'length + 1 downto 0);
  begin
    if Sub = '1' then
      B_i := not unsigned(B);
    else
      B_i := unsigned(B);
    end if;
    R_i := unsigned("0" & A & Cin) + unsigned("0" & B_i & '1');
    Cout <= R_i(A'length + 1);
    Res <= std_logic_vector(R_i(A'length downto 1));
  end procedure;

  signal q_v : std_logic_vector(7 downto 0);
  signal c_v : std_logic;
begin
  addsub(a, b, sub, cin, q_v, c_v);
  q <= q_v;
  c <= c_v;
end architecture;
