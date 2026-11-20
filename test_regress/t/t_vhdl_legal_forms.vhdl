-- DESCRIPTION: Verilator: VHDL legal forms the frontend refuses today
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- Each sub-design below is legal VHDL-2008 that GHDL accepts and simulates;
-- the values the harness checks are the ones GHDL reports.

package legal_k is
  constant K : integer := 42;
end package;

-- (b) positional open in a port map, using the formal's default
entity legal_c2 is
  port (
    p1 : in  bit;
    p2 : in  bit := '1';
    y  : out bit
  );
end;

architecture rtl of legal_c2 is
begin
  y <= p1 and p2;
end;

-- (c) an instantiation with no maps is legal; its outputs stay open
entity legal_c3 is
  port (
    p : in bit := '1';
    y : out bit
  );
end;

architecture rtl of legal_c3 is
begin
  y <= p;
end;

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    o1 : out integer;  -- (a) 16#FF#E2 = 65280
    o2 : out bit;      -- (b) positional open takes p2's default
    o4 : out bit;      -- (d) for i in <subtype> generate
    o5 : out bit;      -- (e) 'view' is a legal 2008 identifier
    o6 : out integer;  -- (f) use of one specific declaration
    o7 : out bit       -- (g) '(' as an enumeration literal and case choice
  );
end;

architecture rtl of t is
  -- (a) a based literal with an exponent (IEEE 1076-2008 15.5)
  constant based : integer := 16#FF#E2;

  -- (d) a discrete subtype as a generate range (IEEE 1076-2008 11.8)
  subtype idx is integer range 0 to 3;
  signal gvec  : bit_vector(0 to 3) := "1111";
  signal gbits : bit_vector(0 to 3);

  -- (e) 'view' is not reserved in VHDL-2008 (IEEE 1076-2008 15.10)
  type view is (alpha, beta);
  signal sview : view := alpha;

  -- (f) a use clause may name one declaration (IEEE 1076-2008 12.4)
  use work.legal_k.K;
begin
  o1 <= based;

  u2 : entity work.legal_c2 port map ('1', open, o2);

  u3 : entity work.legal_c3;

  gen : for i in idx generate
    gbits(i) <= gvec(i);
  end generate;
  o4 <= gbits(0) and gbits(1) and gbits(2) and gbits(3);

  o5 <= '1' when sview = alpha else '0';

  o6 <= K;

  -- (g) '(' is a character literal like any other (IEEE 1076-2008 15.6)
  proc7 : process
    type op is ('(', ')');
    variable c : op := ')';
    variable hit : bit := '0';
  begin
    case c is
      when '(' => hit := '0';
      when ')' => hit := '1';
    end case;
    o7 <= hit;
    wait;
  end process;
end;
