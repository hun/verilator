-- DESCRIPTION: Verilator: VHDL loop over an enumeration subtype
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    a : in std_logic;
    y : out integer;
    z : out integer;
    e : out integer;
    f : out integer;
    g : out integer;
    h : out integer
  );
end;

architecture rtl of t is
  type st is (S0, S1, S2, S3);
  subtype mid is st range S1 to S2;

  function fn(k : integer) return integer is
    variable r : integer := 0;
    variable c : st;
  begin
    for i in mid loop
      c := i;
      if c = S2 then r := r + k; else r := r + 1; end if;
      r := r + st'pos(i) * 100;
    end loop;
    return r;
  end;
begin
  process (a)
    variable n : integer;
    variable m : integer;
    variable c : st;
    variable t : integer;
  begin
    n := 0; m := 0; c := S0; t := 0;
    for i in mid loop
      n := n + st'pos(i);
      if i = S2 then m := m + 10; end if;
      c := i;
      case i is
        when S1 => m := m + 100;
        when others => null;
      end case;
    end loop;
    for i in st'range loop
      m := m + 1;
    end loop;
    for i in st'reverse_range loop
      m := m + st'pos(i) * 1000;
    end loop;
    for i in S1 to S3 loop
      t := t + st'pos(i);
    end loop;
    y <= n;
    z <= m;
    e <= st'pos(c);
    f <= t;
  end process;

  g <= fn(7);

  -- Three nested loops over a four-literal type: 4 * 4 * 4 unrolled bodies
  nest : process (a)
    variable cnt : integer;
  begin
    cnt := 0;
    for i in st loop
      for j in st loop
        for k in st loop
          if i = j and j = k then cnt := cnt + 1; end if;
        end loop;
      end loop;
    end loop;
    h <= cnt;
  end process;
end;
