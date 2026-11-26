-- DESCRIPTION: Verilator: VHDL package constant initialized from a package signal is unsupported
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

package pk is
  signal s : integer := 3;
  constant c : integer := s;
end package pk;
entity t is port (o : out bit); end entity t;
architecture rtl of t is begin o <= '1'; end architecture rtl;
